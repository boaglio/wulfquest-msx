package wulfmsx.convert;

/** The data cannot be made into the MSX2 version: what, and why. */
public final class ConvertException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    public ConvertException(String message) {
        super(message);
    }
}
